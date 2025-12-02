' ai.bas - AI player strategies for Monopoly
' Three distinct AI personalities: Andy (Aggressive), Betty (Conservative), Chip (Adaptive)

' AI Decision helper class
CLASS AIBrain
    DIM strategy AS INTEGER
    DIM reserveMoney AS INTEGER
    DIM targetGroups AS STRING

    SUB Init(strat AS INTEGER)
        strategy = strat

        ' Set up strategy-specific parameters
        IF strat = STRATEGY_AGGRESSIVE THEN
            reserveMoney = 100
            targetGroups = "1,2,3,4,5,6,7,8,9,10"  ' All groups
        END IF
        IF strat = STRATEGY_CONSERVATIVE THEN
            reserveMoney = 500
            targetGroups = "4,5"  ' Orange and Red only
        END IF
        IF strat = STRATEGY_BALANCED THEN
            reserveMoney = 300
            targetGroups = "2,3,4,5,6"  ' Light blue through yellow
        END IF
    END SUB

    ' Should AI buy this property?
    FUNCTION ShouldBuy(prop AS GameProperty, playerMoney AS INTEGER, phase AS INTEGER) AS INTEGER
        DIM cost AS INTEGER
        DIM grp AS INTEGER
        DIM maxSpend AS INTEGER

        cost = prop.GetCost()
        grp = prop.GetGroup()
        ShouldBuy = 0

        ' Check if we have enough money after reserve
        maxSpend = playerMoney - reserveMoney
        IF cost > maxSpend THEN EXIT FUNCTION

        IF strategy = STRATEGY_AGGRESSIVE THEN
            ' Buy everything we can afford
            ShouldBuy = 1
        END IF

        IF strategy = STRATEGY_CONSERVATIVE THEN
            ' Only buy target groups or railroads
            IF grp = GRP_ORANGE THEN ShouldBuy = 1
            IF grp = GRP_RED THEN ShouldBuy = 1
            IF grp = GRP_RAILROAD THEN ShouldBuy = 1
        END IF

        IF strategy = STRATEGY_BALANCED THEN
            ' Adaptive based on game phase
            IF phase < 10 THEN
                ' Early game: buy most things
                ShouldBuy = 1
            ELSE
                ' Mid/late game: focus on completing sets
                IF Me.IsTargetGroup(grp) = 1 THEN ShouldBuy = 1
            END IF
        END IF
    END FUNCTION

    ' Check if group is in target list
    FUNCTION IsTargetGroup(grp AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM part AS STRING
        DIM commaPos AS INTEGER
        DIM remaining AS STRING

        IsTargetGroup = 0
        remaining = targetGroups

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

            IF VAL(part) = grp THEN
                IsTargetGroup = 1
                EXIT FUNCTION
            END IF
        LOOP
    END FUNCTION

    ' Get maximum auction bid for property
    FUNCTION GetMaxBid(prop AS GameProperty, playerMoney AS INTEGER) AS INTEGER
        DIM baseCost AS INTEGER
        DIM maxBid AS INTEGER
        DIM multiplier AS SINGLE

        baseCost = prop.GetCost()

        IF strategy = STRATEGY_AGGRESSIVE THEN
            multiplier = 1.5  ' Bid up to 150% of value
        END IF
        IF strategy = STRATEGY_CONSERVATIVE THEN
            multiplier = 0.8  ' Only bid up to 80% of value
        END IF
        IF strategy = STRATEGY_BALANCED THEN
            multiplier = 1.1  ' Bid slightly above value
        END IF

        maxBid = INT(baseCost * multiplier)

        ' Don't bid more than we can afford minus reserve
        IF maxBid > playerMoney - reserveMoney THEN
            maxBid = playerMoney - reserveMoney
        END IF

        IF maxBid < 0 THEN maxBid = 0
        GetMaxBid = maxBid
    END FUNCTION

    ' Should AI build a house on this property?
    FUNCTION ShouldBuild(prop AS GameProperty, playerMoney AS INTEGER, hasMonopoly AS INTEGER) AS INTEGER
        DIM houseCost AS INTEGER

        ShouldBuild = 0
        IF hasMonopoly = 0 THEN EXIT FUNCTION
        IF prop.CanBuild() = 0 THEN EXIT FUNCTION

        houseCost = prop.GetHouseCost()

        IF strategy = STRATEGY_AGGRESSIVE THEN
            ' Build if we can afford it
            IF playerMoney > houseCost + 50 THEN ShouldBuild = 1
        END IF

        IF strategy = STRATEGY_CONSERVATIVE THEN
            ' Only build if we have plenty of cash
            IF playerMoney > houseCost + reserveMoney + 200 THEN ShouldBuild = 1
        END IF

        IF strategy = STRATEGY_BALANCED THEN
            ' Build with moderate reserve
            IF playerMoney > houseCost + reserveMoney THEN ShouldBuild = 1
        END IF
    END FUNCTION

    ' Should AI accept a trade offer?
    FUNCTION ShouldAcceptTrade(giveValue AS INTEGER, getValue AS INTEGER, completesMonopoly AS INTEGER) AS INTEGER
        DIM ratio AS SINGLE

        ShouldAcceptTrade = 0
        IF giveValue = 0 THEN
            IF getValue > 0 THEN ShouldAcceptTrade = 1
            EXIT FUNCTION
        END IF

        ratio = getValue / giveValue

        IF strategy = STRATEGY_AGGRESSIVE THEN
            ' Accept trades that complete monopolies
            IF completesMonopoly = 1 THEN
                IF ratio > 0.5 THEN ShouldAcceptTrade = 1
            ELSE
                IF ratio >= 1.0 THEN ShouldAcceptTrade = 1
            END IF
        END IF

        IF strategy = STRATEGY_CONSERVATIVE THEN
            ' Only accept clearly favorable trades
            IF ratio >= 1.2 THEN ShouldAcceptTrade = 1
        END IF

        IF strategy = STRATEGY_BALANCED THEN
            ' Accept fair trades, especially for monopolies
            IF completesMonopoly = 1 THEN
                IF ratio > 0.7 THEN ShouldAcceptTrade = 1
            ELSE
                IF ratio >= 0.9 THEN ShouldAcceptTrade = 1
            END IF
        END IF
    END FUNCTION

    ' Should AI pay to get out of jail?
    FUNCTION ShouldPayJailFine(playerMoney AS INTEGER, turnNumber AS INTEGER, jailTurns AS INTEGER) AS INTEGER
        ShouldPayJailFine = 0

        IF playerMoney < JAIL_FINE THEN EXIT FUNCTION

        IF strategy = STRATEGY_AGGRESSIVE THEN
            ' Always pay to get out early in game
            IF turnNumber < 30 THEN ShouldPayJailFine = 1
        END IF

        IF strategy = STRATEGY_CONSERVATIVE THEN
            ' Stay in jail as long as possible (late game especially)
            IF jailTurns >= MAX_JAIL_TURNS THEN ShouldPayJailFine = 1
        END IF

        IF strategy = STRATEGY_BALANCED THEN
            ' Pay early game, stay late game
            IF turnNumber < 20 THEN
                ShouldPayJailFine = 1
            ELSE
                IF jailTurns >= 2 THEN ShouldPayJailFine = 1
            END IF
        END IF
    END FUNCTION

    ' Should AI mortgage this property?
    FUNCTION ShouldMortgage(prop AS GameProperty, playerMoney AS INTEGER, needMoney AS INTEGER) AS INTEGER
        ShouldMortgage = 0

        IF prop.IsMortgaged() = 1 THEN EXIT FUNCTION
        IF prop.GetHouses() > 0 THEN EXIT FUNCTION

        ' Only mortgage if we need the money
        IF playerMoney >= needMoney THEN EXIT FUNCTION

        IF strategy = STRATEGY_AGGRESSIVE THEN
            ' Mortgage utilities first, then railroads
            IF prop.GetType() = SPACE_UTILITY THEN ShouldMortgage = 1
            IF prop.GetType() = SPACE_RAILROAD THEN
                IF playerMoney < needMoney - 100 THEN ShouldMortgage = 1
            END IF
        END IF

        IF strategy = STRATEGY_CONSERVATIVE THEN
            ' Mortgage non-target group properties first
            IF Me.IsTargetGroup(prop.GetGroup()) = 0 THEN ShouldMortgage = 1
        END IF

        IF strategy = STRATEGY_BALANCED THEN
            ' Mortgage least valuable first
            IF prop.GetCost() < 150 THEN ShouldMortgage = 1
        END IF
    END FUNCTION

    ' Get AI's preferred build priority (returns property index to build on, -1 if none)
    FUNCTION GetBuildPriority(ownedProps AS STRING, props() AS GameProperty, playerMoney AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM propIdx AS INTEGER
        DIM part AS STRING
        DIM commaPos AS INTEGER
        DIM remaining AS STRING
        DIM bestIdx AS INTEGER
        DIM bestPriority AS INTEGER
        DIM priority AS INTEGER
        DIM houses AS INTEGER

        bestIdx = -1
        bestPriority = 0
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
            propIdx = VAL(part)

            IF props(propIdx).CanBuild() = 1 THEN
                houses = props(propIdx).GetHouses()
                ' Prioritize even building
                priority = 10 - houses

                IF strategy = STRATEGY_AGGRESSIVE THEN
                    ' Prioritize high-rent properties
                    IF props(propIdx).GetGroup() = GRP_DARKBLUE THEN priority = priority + 5
                    IF props(propIdx).GetGroup() = GRP_GREEN THEN priority = priority + 4
                END IF

                IF strategy = STRATEGY_CONSERVATIVE THEN
                    ' Prioritize target groups
                    IF props(propIdx).GetGroup() = GRP_ORANGE THEN priority = priority + 5
                    IF props(propIdx).GetGroup() = GRP_RED THEN priority = priority + 4
                END IF

                IF priority > bestPriority THEN
                    IF playerMoney > props(propIdx).GetHouseCost() + reserveMoney THEN
                        bestPriority = priority
                        bestIdx = propIdx
                    END IF
                END IF
            END IF
        LOOP

        GetBuildPriority = bestIdx
    END FUNCTION

    ' Get AI personality description
    FUNCTION GetPersonality() AS STRING
        IF strategy = STRATEGY_AGGRESSIVE THEN
            GetPersonality = "aggressive"
        END IF
        IF strategy = STRATEGY_CONSERVATIVE THEN
            GetPersonality = "conservative"
        END IF
        IF strategy = STRATEGY_BALANCED THEN
            GetPersonality = "balanced"
        END IF
    END FUNCTION

    ' Get AI comment based on situation
    FUNCTION GetComment(situation AS INTEGER) AS STRING
        DIM comments(2) AS STRING
        DIM idx AS INTEGER

        idx = INT(RND() * 3)

        ' Situation 1: Bought property
        IF situation = 1 THEN
            IF strategy = STRATEGY_AGGRESSIVE THEN
                comments(0) = "Mine! All mine!"
                comments(1) = "I'll take that!"
                comments(2) = "Another one for my empire!"
            END IF
            IF strategy = STRATEGY_CONSERVATIVE THEN
                comments(0) = "A wise investment."
                comments(1) = "This fits my portfolio."
                comments(2) = "Calculated risk."
            END IF
            IF strategy = STRATEGY_BALANCED THEN
                comments(0) = "Good opportunity."
                comments(1) = "This could work."
                comments(2) = "Strategic purchase."
            END IF
        END IF

        ' Situation 2: Paid rent
        IF situation = 2 THEN
            IF strategy = STRATEGY_AGGRESSIVE THEN
                comments(0) = "Grr... I'll get you back!"
                comments(1) = "Just you wait!"
                comments(2) = "This isn't over!"
            END IF
            IF strategy = STRATEGY_CONSERVATIVE THEN
                comments(0) = "Part of the game."
                comments(1) = "Noted in my ledger."
                comments(2) = "An expected expense."
            END IF
            IF strategy = STRATEGY_BALANCED THEN
                comments(0) = "Fair enough."
                comments(1) = "Well played."
                comments(2) = "That stings a bit."
            END IF
        END IF

        ' Situation 3: Built house
        IF situation = 3 THEN
            IF strategy = STRATEGY_AGGRESSIVE THEN
                comments(0) = "Building my empire!"
                comments(1) = "Hotels coming soon!"
                comments(2) = "Fear my development!"
            END IF
            IF strategy = STRATEGY_CONSERVATIVE THEN
                comments(0) = "Steady growth."
                comments(1) = "Careful expansion."
                comments(2) = "Secure investment."
            END IF
            IF strategy = STRATEGY_BALANCED THEN
                comments(0) = "Good timing."
                comments(1) = "Improving value."
                comments(2) = "Smart building."
            END IF
        END IF

        ' Situation 4: Went to jail
        IF situation = 4 THEN
            IF strategy = STRATEGY_AGGRESSIVE THEN
                comments(0) = "I'll be out soon!"
                comments(1) = "This won't stop me!"
                comments(2) = "Just a setback!"
            END IF
            IF strategy = STRATEGY_CONSERVATIVE THEN
                comments(0) = "A safe place to rest."
                comments(1) = "Free accommodations!"
                comments(2) = "I'll enjoy the break."
            END IF
            IF strategy = STRATEGY_BALANCED THEN
                comments(0) = "Could be worse."
                comments(1) = "Time to plan."
                comments(2) = "Temporary situation."
            END IF
        END IF

        GetComment = comments(idx)
    END FUNCTION
END CLASS

