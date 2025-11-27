REM ====================================================================
REM MONOPOLY - AI Logic
REM Different AI strategies for computer opponents
REM ====================================================================

REM AI type constants
DIM AI_AGGRESSIVE AS INTEGER
DIM AI_CONSERVATIVE AS INTEGER
DIM AI_BALANCED AS INTEGER

AI_AGGRESSIVE = 0
AI_CONSERVATIVE = 1
AI_BALANCED = 2

REM ====================================================================
REM Decide if AI should buy a property
REM ====================================================================
FUNCTION AIDecideBuy(playerIdx AS INTEGER, propIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM cost AS INTEGER
    DIM money AS INTEGER
    DIM aiType AS INTEGER
    DIM groupOwned AS INTEGER
    DIM groupSize AS INTEGER
    DIM shouldBuy AS INTEGER
    DIM reserveMoney AS INTEGER

    p = players.get_Item(playerIdx)
    prop = properties.get_Item(propIdx)

    cost = prop.GetCost()
    money = p.GetMoney()
    aiType = p.GetAIType()
    groupOwned = CountGroupOwned(playerIdx, prop.GetGroup())
    groupSize = GetGroupSize(prop.GetGroup())

    shouldBuy = 0

    IF aiType = AI_AGGRESSIVE THEN
        REM Aggressive: Buy if can afford, keep $100 reserve
        reserveMoney = 100
        IF money - cost >= reserveMoney THEN
            shouldBuy = 1
        END IF

    ELSEIF aiType = AI_CONSERVATIVE THEN
        REM Conservative: Only buy if completing group or have lots of money
        reserveMoney = 300

        REM Would complete the group
        IF groupOwned = groupSize - 1 THEN
            IF money - cost >= 50 THEN
                shouldBuy = 1
            END IF
        END IF

        REM Already own one in group
        IF shouldBuy = 0 THEN
            IF groupOwned > 0 THEN
                IF money - cost >= reserveMoney THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF

        REM Railroad or utility
        IF shouldBuy = 0 THEN
            IF prop.GetGroup() = GROUP_RAILROAD THEN
                IF money - cost >= reserveMoney THEN
                    shouldBuy = 1
                END IF
            END IF
            IF prop.GetGroup() = GROUP_UTILITY THEN
                IF money - cost >= reserveMoney THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF

        REM Really have lots of money
        IF shouldBuy = 0 THEN
            IF money > 1000 THEN
                IF money - cost >= 500 THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF

    ELSE
        REM Balanced: Mix of strategies
        reserveMoney = 200

        REM Always try to complete groups
        IF groupOwned = groupSize - 1 THEN
            IF money - cost >= 100 THEN
                shouldBuy = 1
            END IF
        END IF

        REM Buy railroads
        IF shouldBuy = 0 THEN
            IF prop.GetGroup() = GROUP_RAILROAD THEN
                IF money - cost >= reserveMoney THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF

        REM Buy if reasonable money
        IF shouldBuy = 0 THEN
            IF money - cost >= reserveMoney THEN
                REM 70% chance to buy
                IF RND() < 0.7 THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF
    END IF

    AIDecideBuy = shouldBuy
END FUNCTION

REM ====================================================================
REM AI auction bid amount
REM ====================================================================
FUNCTION AIAuctionBid(playerIdx AS INTEGER, propIdx AS INTEGER, currentBid AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM maxBid AS INTEGER
    DIM aiType AS INTEGER
    DIM groupOwned AS INTEGER
    DIM groupSize AS INTEGER
    DIM bid AS INTEGER

    p = players.get_Item(playerIdx)
    prop = properties.get_Item(propIdx)
    aiType = p.GetAIType()
    groupOwned = CountGroupOwned(playerIdx, prop.GetGroup())
    groupSize = GetGroupSize(prop.GetGroup())

    REM Calculate max bid based on AI type
    IF aiType = AI_AGGRESSIVE THEN
        maxBid = prop.GetCost() + 50
        IF groupOwned = groupSize - 1 THEN
            maxBid = prop.GetCost() + 150
        END IF
    ELSEIF aiType = AI_CONSERVATIVE THEN
        maxBid = prop.GetCost() - 50
        IF maxBid < 50 THEN
            maxBid = 50
        END IF
        IF groupOwned = groupSize - 1 THEN
            maxBid = prop.GetCost() + 50
        END IF
    ELSE
        maxBid = prop.GetCost()
        IF groupOwned = groupSize - 1 THEN
            maxBid = prop.GetCost() + 100
        END IF
    END IF

    REM Don't bid more than we can afford minus reserve
    IF maxBid > p.GetMoney() - 50 THEN
        maxBid = p.GetMoney() - 50
    END IF

    REM Calculate actual bid
    IF currentBid < maxBid THEN
        bid = currentBid + 10
        IF bid > maxBid THEN
            bid = 0
        END IF
    ELSE
        bid = 0
    END IF

    AIAuctionBid = bid
END FUNCTION

REM ====================================================================
REM Decide if AI should build houses
REM ====================================================================
FUNCTION AIDecideBuild(playerIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM canBuild AS INTEGER
    DIM aiType AS INTEGER
    DIM minHouses AS INTEGER
    DIM maxHouses AS INTEGER
    DIM grp AS INTEGER
    DIM propToBuild AS INTEGER
    DIM j AS INTEGER
    DIM prop2 AS PropData
    DIM groupComplete AS INTEGER
    DIM reserveMoney AS INTEGER
    DIM houseCost AS INTEGER

    p = players.get_Item(playerIdx)
    aiType = p.GetAIType()
    propToBuild = -1

    IF aiType = AI_AGGRESSIVE THEN
        reserveMoney = 100
    ELSEIF aiType = AI_CONSERVATIVE THEN
        reserveMoney = 400
    ELSE
        reserveMoney = 250
    END IF

    REM Check each owned property
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.IsMortgaged() = 0 THEN
                grp = prop.GetGroup()
                IF grp < GROUP_RAILROAD THEN
                    REM Check if group is complete
                    groupComplete = OwnsCompleteGroup(playerIdx, grp)
                    IF groupComplete = 1 THEN
                        houseCost = prop.GetHouseCost()
                        IF prop.GetHouses() < 5 THEN
                            IF p.GetMoney() - houseCost >= reserveMoney THEN
                                REM Check even build rule
                                minHouses = 99
                                maxHouses = 0
                                j = 0
                                WHILE j < properties.Count
                                    prop2 = properties.get_Item(j)
                                    IF prop2.GetGroup() = grp THEN
                                        IF prop2.GetOwner() = playerIdx THEN
                                            IF prop2.GetHouses() < minHouses THEN
                                                minHouses = prop2.GetHouses()
                                            END IF
                                            IF prop2.GetHouses() > maxHouses THEN
                                                maxHouses = prop2.GetHouses()
                                            END IF
                                        END IF
                                    END IF
                                    j = j + 1
                                WEND

                                REM Can only build if this property has minimum houses
                                IF prop.GetHouses() = minHouses THEN
                                    propToBuild = i
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    AIDecideBuild = propToBuild
END FUNCTION

REM ====================================================================
REM Decide if AI should mortgage a property
REM ====================================================================
FUNCTION AIDecideMortgage(playerIdx AS INTEGER, neededMoney AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM propToMortgage AS INTEGER
    DIM lowestValue AS INTEGER
    DIM mortgageValue AS INTEGER
    DIM raisedMoney AS INTEGER

    p = players.get_Item(playerIdx)
    propToMortgage = -1
    lowestValue = 99999
    raisedMoney = 0

    REM First try to mortgage properties without houses
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.IsMortgaged() = 0 THEN
                IF prop.GetHouses() = 0 THEN
                    mortgageValue = prop.GetMortgageValue()
                    IF mortgageValue < lowestValue THEN
                        lowestValue = mortgageValue
                        propToMortgage = i
                    END IF
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    AIDecideMortgage = propToMortgage
END FUNCTION

REM ====================================================================
REM Decide if AI should sell houses
REM ====================================================================
FUNCTION AIDecideSellHouse(playerIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM propToSell AS INTEGER
    DIM maxHouses AS INTEGER
    DIM grp AS INTEGER
    DIM j AS INTEGER
    DIM prop2 AS PropData
    DIM groupMaxHouses AS INTEGER

    p = players.get_Item(playerIdx)
    propToSell = -1
    maxHouses = 0

    REM Find property with most houses that can be sold (even build rule)
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.GetHouses() > 0 THEN
                grp = prop.GetGroup()

                REM Find max houses in group
                groupMaxHouses = 0
                j = 0
                WHILE j < properties.Count
                    prop2 = properties.get_Item(j)
                    IF prop2.GetGroup() = grp THEN
                        IF prop2.GetOwner() = playerIdx THEN
                            IF prop2.GetHouses() > groupMaxHouses THEN
                                groupMaxHouses = prop2.GetHouses()
                            END IF
                        END IF
                    END IF
                    j = j + 1
                WEND

                REM Can only sell if this has max houses in group
                IF prop.GetHouses() = groupMaxHouses THEN
                    IF prop.GetHouses() > maxHouses THEN
                        maxHouses = prop.GetHouses()
                        propToSell = i
                    END IF
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    AIDecideSellHouse = propToSell
END FUNCTION

REM ====================================================================
REM AI jail decision: pay, use card, or try to roll
REM Returns: 0=roll, 1=pay, 2=use card
REM ====================================================================
FUNCTION AIJailDecision(playerIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM decision AS INTEGER
    DIM aiType AS INTEGER
    DIM money AS INTEGER
    DIM jailTurns AS INTEGER

    p = players.get_Item(playerIdx)
    aiType = p.GetAIType()
    money = p.GetMoney()
    jailTurns = p.GetJailTurns()

    decision = 0

    REM If has jail free card, use it (usually)
    IF p.HasJailFreeCard() = 1 THEN
        IF jailTurns >= 2 THEN
            decision = 2
        ELSEIF aiType = AI_AGGRESSIVE THEN
            decision = 2
        ELSEIF RND() < 0.5 THEN
            decision = 2
        END IF
    END IF

    REM If on third turn, must pay or use card
    IF decision = 0 THEN
        IF jailTurns >= 2 THEN
            IF money >= 50 THEN
                decision = 1
            END IF
        END IF
    END IF

    REM Aggressive AI pays early if wealthy
    IF decision = 0 THEN
        IF aiType = AI_AGGRESSIVE THEN
            IF money > 500 THEN
                IF RND() < 0.7 THEN
                    decision = 1
                END IF
            END IF
        END IF
    END IF

    REM Stay in jail if in late game (balanced/conservative)
    IF decision = 0 THEN
        IF aiType <> AI_AGGRESSIVE THEN
            REM Stay in jail is default (try to roll doubles)
            decision = 0
        END IF
    END IF

    AIJailDecision = decision
END FUNCTION

REM ====================================================================
REM AI trade evaluation - would AI accept this trade?
REM Simplified due to compiler bugs with nested IF/ELSEIF (BUG-OOP-012)
REM ====================================================================
FUNCTION AIEvaluateTrade(playerIdx AS INTEGER, givingProps AS Viper.Collections.List, gettingProps AS Viper.Collections.List, givingMoney AS INTEGER, gettingMoney AS INTEGER) AS INTEGER
    REM Trading disabled - always reject
    AIEvaluateTrade = 0
END FUNCTION

REM ====================================================================
REM AI makes a trade offer (returns property index to request, -1 if none)
REM ====================================================================
FUNCTION AIMakeTradeOffer(playerIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM targetProp AS INTEGER
    DIM grpOwned AS INTEGER
    DIM grpSize AS INTEGER
    DIM grp AS INTEGER

    p = players.get_Item(playerIdx)
    targetProp = -1

    REM Look for properties that would complete our groups
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        grp = prop.GetGroup()
        IF grp < GROUP_RAILROAD THEN
            IF prop.GetOwner() <> playerIdx THEN
                IF prop.GetOwner() >= 0 THEN
                    grpOwned = CountGroupOwned(playerIdx, grp)
                    grpSize = GetGroupSize(grp)
                    IF grpOwned = grpSize - 1 THEN
                        targetProp = i
                    END IF
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    AIMakeTradeOffer = targetProp
END FUNCTION

REM ====================================================================
REM Wait for AI turn (add delay so human can follow)
REM ====================================================================
SUB AIDelay()
    SLEEP 500
END SUB

REM ====================================================================
REM AI announce decision
REM ====================================================================
SUB AIAnnounce(playerIdx AS INTEGER, msg AS STRING)
    DIM p AS Player
    p = players.get_Item(playerIdx)
    ShowMessage(p.GetName() + " (AI): " + msg)
    AIDelay()
END SUB

